#include <unistd.h>
#include <ncurses.h>
#include <ctype.h>
#include <cstring>
#include <stdlib.h>
#include <limits.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include "io.h"
#include "character.h"
#include "poke327.h"
#include "db_parse.h"
using namespace std;
typedef struct io_message {
  /* Will print " --more-- " at end of line when another message follows. *
   * Leave 10 extra spaces for that.                                      */
  char msg[71];
  struct io_message *next;
} io_message_t;

static io_message_t *io_head, *io_tail;

void io_init_terminal(void)
{
  initscr();
  raw();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  start_color();
  init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
  init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
  init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
  init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
  init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
  init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
}

void io_reset_terminal(void)
{
  endwin();

  while (io_head) {
    io_tail = io_head;
    io_head = io_head->next;
    free(io_tail);
  }
  io_tail = NULL;
}

void io_queue_message(const char *format, ...)
{
  io_message_t *tmp;
  va_list ap;

  if (!(tmp = (io_message_t *) malloc(sizeof (*tmp)))) {
    perror("malloc");
    exit(1);
  }

  tmp->next = NULL;

  va_start(ap, format);

  vsnprintf(tmp->msg, sizeof (tmp->msg), format, ap);

  va_end(ap);

  if (!io_head) {
    io_head = io_tail = tmp;
  } else {
    io_tail->next = tmp;
    io_tail = tmp;
  }
}

static void io_print_message_queue(uint32_t y, uint32_t x)
{
  while (io_head) {
    io_tail = io_head;
    attron(COLOR_PAIR(COLOR_CYAN));
    mvprintw(y, x, "%-80s", io_head->msg);
    attroff(COLOR_PAIR(COLOR_CYAN));
    io_head = io_head->next;
    if (io_head) {
      attron(COLOR_PAIR(COLOR_CYAN));
      mvprintw(y, x + 70, "%10s", " --more-- ");
      attroff(COLOR_PAIR(COLOR_CYAN));
      refresh();
      getch();
    }
    free(io_tail);
  }
  io_tail = NULL;
}

/**************************************************************************
 * Compares trainer distances from the PC according to the rival distance *
 * map.  This gives the approximate distance that the PC must travel to   *
 * get to the trainer (doesn't account for crossing buildings).  This is  *
 * not the distance from the NPC to the PC unless the NPC is a rival.     *
 *                                                                        *
 * Not a bug.                                                             *
 **************************************************************************/
static int compare_trainer_distance(const void *v1, const void *v2)
{
  const Character *const *c1 = (const Character *const *) v1;
  const Character *const *c2 = (const Character *const *) v2;

  return (world.rival_dist[(*c1)->pos[dim_y]][(*c1)->pos[dim_x]] -
          world.rival_dist[(*c2)->pos[dim_y]][(*c2)->pos[dim_x]]);
}

static Character *io_nearest_visible_trainer()
{
  Character **c, *n;
  uint32_t x, y, count;

  c = (Character **) malloc(world.cur_map->num_trainers * sizeof (*c));

  /* Get a linear list of trainers */
  for (count = 0, y = 1; y < MAP_Y - 1; y++) {
    for (x = 1; x < MAP_X - 1; x++) {
      if (world.cur_map->cmap[y][x] && world.cur_map->cmap[y][x] !=
          &world.pc) {
        c[count++] = world.cur_map->cmap[y][x];
      }
    }
  }

  /* Sort it by distance from PC */
  qsort(c, count, sizeof (*c), compare_trainer_distance);

  n = c[0];

  free(c);

  return n;
}

void io_display()
{
  uint32_t y, x;
  Character *c;

  clear();
  for (y = 0; y < MAP_Y; y++) {
    for (x = 0; x < MAP_X; x++) {
      if (world.cur_map->cmap[y][x]) {
        mvaddch(y + 1, x, world.cur_map->cmap[y][x]->symbol);
      } else {
        switch (world.cur_map->map[y][x]) {
        case ter_boulder:
        case ter_mountain:
          attron(COLOR_PAIR(COLOR_MAGENTA));
          mvaddch(y + 1, x, '%');
          attroff(COLOR_PAIR(COLOR_MAGENTA));
          break;
        case ter_tree:
        case ter_forest:
          attron(COLOR_PAIR(COLOR_GREEN));
          mvaddch(y + 1, x, '^');
          attroff(COLOR_PAIR(COLOR_GREEN));
          break;
        case ter_path:
        case ter_exit:
          attron(COLOR_PAIR(COLOR_YELLOW));
          mvaddch(y + 1, x, '#');
          attroff(COLOR_PAIR(COLOR_YELLOW));
          break;
        case ter_mart:
          attron(COLOR_PAIR(COLOR_BLUE));
          mvaddch(y + 1, x, 'M');
          attroff(COLOR_PAIR(COLOR_BLUE));
          break;
        case ter_center:
          attron(COLOR_PAIR(COLOR_RED));
          mvaddch(y + 1, x, 'C');
          attroff(COLOR_PAIR(COLOR_RED));
          break;
        case ter_grass:
          attron(COLOR_PAIR(COLOR_GREEN));
          mvaddch(y + 1, x, ':');
          attroff(COLOR_PAIR(COLOR_GREEN));
          break;
        case ter_clearing:
          attron(COLOR_PAIR(COLOR_GREEN));
          mvaddch(y + 1, x, '.');
          attroff(COLOR_PAIR(COLOR_GREEN));
          break;
        default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
          attron(COLOR_PAIR(COLOR_CYAN));
          mvaddch(y + 1, x, '0');
          attroff(COLOR_PAIR(COLOR_CYAN));
       }
      }
    }
  }

  mvprintw(23, 1, "PC position is (%2d,%2d) on map %d%cx%d%c.",
           world.pc.pos[dim_x],
           world.pc.pos[dim_y],
           abs(world.cur_idx[dim_x] - (WORLD_SIZE / 2)),
           world.cur_idx[dim_x] - (WORLD_SIZE / 2) >= 0 ? 'E' : 'W',
           abs(world.cur_idx[dim_y] - (WORLD_SIZE / 2)),
           world.cur_idx[dim_y] - (WORLD_SIZE / 2) <= 0 ? 'N' : 'S');
  mvprintw(22, 1, "%d known %s.", world.cur_map->num_trainers,
           world.cur_map->num_trainers > 1 ? "trainers" : "trainer");
  mvprintw(22, 30, "Nearest visible trainer: ");
  if ((c = io_nearest_visible_trainer())) {
    attron(COLOR_PAIR(COLOR_RED));
    mvprintw(22, 55, "%c at %d %c by %d %c.",
             c->symbol,
             abs(c->pos[dim_y] - world.pc.pos[dim_y]),
             ((c->pos[dim_y] - world.pc.pos[dim_y]) <= 0 ?
              'N' : 'S'),
             abs(c->pos[dim_x] - world.pc.pos[dim_x]),
             ((c->pos[dim_x] - world.pc.pos[dim_x]) <= 0 ?
              'W' : 'E'));
    attroff(COLOR_PAIR(COLOR_RED));
  } else {
    attron(COLOR_PAIR(COLOR_BLUE));
    mvprintw(22, 55, "NONE.");
    attroff(COLOR_PAIR(COLOR_BLUE));
  }

  io_print_message_queue(0, 0);

  refresh();
}

uint32_t io_teleport_pc(pair_t dest)
{
  /* Just for fun. And debugging.  Mostly debugging. */

  do {
    dest[dim_x] = rand_range(1, MAP_X - 2);
    dest[dim_y] = rand_range(1, MAP_Y - 2);
  } while (world.cur_map->cmap[dest[dim_y]][dest[dim_x]]                  ||
           move_cost[char_pc][world.cur_map->map[dest[dim_y]]
                                                [dest[dim_x]]] == INT_MAX ||
           world.rival_dist[dest[dim_y]][dest[dim_x]] < 0);

  return 0;
}

static void io_scroll_trainer_list(char (*s)[40], uint32_t count)
{
  uint32_t offset;
  uint32_t i;

  offset = 0;

  while (1) {
    for (i = 0; i < 13; i++) {
      mvprintw(i + 6, 19, " %-40s ", s[i + offset]);
    }
    switch (getch()) {
    case KEY_UP:
      if (offset) {
        offset--;
      }
      break;
    case KEY_DOWN:
      if (offset < (count - 13)) {
        offset++;
      }
      break;
    case 27:
      return;
    }

  }
}

static void io_list_trainers_display(Npc **c,
                                     uint32_t count)
{
  uint32_t i;
  char (*s)[40]; /* pointer to array of 40 char */

  s = (char (*)[40]) malloc(count * sizeof (*s));

  mvprintw(3, 19, " %-40s ", "");
  /* Borrow the first element of our array for this string: */
  snprintf(s[0], 40, "You know of %d trainers:", count);
  mvprintw(4, 19, " %-40s ", s[0]);
  mvprintw(5, 19, " %-40s ", "");

  for (i = 0; i < count; i++) {
    snprintf(s[i], 40, "%16s %c: %2d %s by %2d %s",
             char_type_name[c[i]->ctype],
             c[i]->symbol,
             abs(c[i]->pos[dim_y] - world.pc.pos[dim_y]),
             ((c[i]->pos[dim_y] - world.pc.pos[dim_y]) <= 0 ?
              "North" : "South"),
             abs(c[i]->pos[dim_x] - world.pc.pos[dim_x]),
             ((c[i]->pos[dim_x] - world.pc.pos[dim_x]) <= 0 ?
              "West" : "East"));
    if (count <= 13) {
      /* Handle the non-scrolling case right here. *
       * Scrolling in another function.            */
      mvprintw(i + 6, 19, " %-40s ", s[i]);
    }
  }

  if (count <= 13) {
    mvprintw(count + 6, 19, " %-40s ", "");
    mvprintw(count + 7, 19, " %-40s ", "Hit escape to continue.");
    while (getch() != 27 /* escape */)
      ;
  } else {
    mvprintw(19, 19, " %-40s ", "");
    mvprintw(20, 19, " %-40s ",
             "Arrows to scroll, escape to continue.");
    io_scroll_trainer_list(s, count);
  }

  free(s);
}

static void io_list_trainers()
{
  Character **c;
  uint32_t x, y, count;

  c = (Character **) malloc(world.cur_map->num_trainers * sizeof (*c));

  /* Get a linear list of trainers */
  for (count = 0, y = 1; y < MAP_Y - 1; y++) {
    for (x = 1; x < MAP_X - 1; x++) {
      if (world.cur_map->cmap[y][x] && world.cur_map->cmap[y][x] !=
          &world.pc) {
        c[count++] = world.cur_map->cmap[y][x];
      }
    }
  }

  /* Sort it by distance from PC */
  qsort(c, count, sizeof (*c), compare_trainer_distance);

  /* Display it */
  io_list_trainers_display((Npc **)(c), count);
  free(c);

  /* And redraw the map */
  io_display();
}

void io_pokemart()
{
  WINDOW *pokemart = newwin(12,70,6,18);
  mvprintw(6, 19, "Welcome to the Pokemart.  Could I interest you in some Pokeballs?");
  mvprintw(7,19,"Pokeballs: %d      |PokeBux: %d      |Potions: %d      | Revives: %d ",world.pc.pokeballs,world.pc.pokebux,world.pc.potions,world.pc.revives);
    mvprintw(8,19, "Press 1. to buy Potions");
    mvprintw(9,19, "Press 2. to buy Pokeballs");
    mvprintw(10,19, "Press 3. to buy Revives");

  wrefresh(pokemart);
  char c;
  while((c = getch()) != 27){
    wrefresh(pokemart);
    switch (c)
    {
    case '1':
      if(world.pc.pokebux >= 10){
        world.pc.pokebux -= 10;
        world.pc.potions += 1;
      }else{
        mvprintw(11,19,"You don't have enough PokeBux!");
      }
      break;
    case '2':
      if(world.pc.pokebux >= 20){
        world.pc.pokebux -= 20;
        world.pc.pokeballs += 1;
      }else{
        mvprintw(11,19,"You don't have enough PokeBux!");
      }
      break;
    case '3':
      if(world.pc.pokebux >= 30){
        world.pc.pokebux -= 30;
        world.pc.revives += 1;
      }else{
        mvprintw(11,19,"You don't have enough PokeBux!");
      }
      break;


    default:
      mvprintw(11,19,"Invalid input!");
      break;
    }

    mvprintw(7,19,"Pokeballs: %d      |PokeBux: %d      |Potions: %d      | Revives: %d ",world.pc.pokeballs,world.pc.pokebux,world.pc.potions,world.pc.revives);
  }
  endwin();
}
void io_save_pokemon()
{
  int pokeNum = getch() - '0';
  if(pokeNum <= world.pc.inventory.size())
  {
    world.storage.push_back(world.pc.inventory.at(pokeNum - 1));
    world.pc.inventory.erase(world.pc.inventory.begin() + pokeNum - 1);
  }else{
    mvprintw(0,0,"Pokemon not found!");
  }
}
void io_load_pokemon()
{
  int pokeNum = getch() - '0';
  if(world.pc.inventory.size() == 6)
  {
    mvprintw(0,0,"You can't have more than 6 Pokemon!");
  }else if(world.storage.size() == 0)
  {
    mvprintw(0,0,"There are no Pokemon to load!");
  }else{
     world.pc.inventory.push_back(world.storage.at(pokeNum - 1));
      world.storage.erase(world.storage.begin() + pokeNum - 1);
  }
}
void io_pokemon_center()
{
  WINDOW *pokemon_center = newwin(12,70,6,18);
  mvprintw(6, 19, "Welcome to the Pokemon Center.  How can Nurse Joy assist you?");
  mvprintw(7,19,"Select a Pokemon to heal up! or hit s to save pokemon to storage or t to take from storage");
  for(int i = 0; i < world.pc.inventory.size(); i++){
      mvprintw(8+i,19,"%d. %s HP: %2d Level: %d",i+1,world.pc.inventory.at(i).identifier,world.pc.inventory.at(i).hp,world.pc.inventory.at(i).level);
  }
  if(world.storage.size() > 0){
    mvprintw(world.pc.inventory.size() + 8,19,"Storage:");
    for(int i = 0; i < world.storage.size(); i++){
      mvprintw(8+world.pc.inventory.size()+1+i,19,"%d. %s HP: %2d Level: %d",i+1,world.storage.at(i).identifier,world.storage.at(i).hp,world.storage.at(i).level);
  }
  }
  wrefresh(pokemon_center);
  char p;
  while(( p = getch()) != 27){

    switch(p){
      case 's':
      io_save_pokemon();
      break;
      case 't':
      io_load_pokemon();
      break;
      default:
      int pokeNum = p - '0';
      if(pokeNum <= world.pc.inventory.size())
       {

         world.pc.inventory.at(pokeNum - 1).hp = world.pc.inventory.at(pokeNum - 1).default_hp;
       }else{
        mvprintw(0,0,"Invalid input!");
      }
      break;
    }
  werase(pokemon_center);
  mvprintw(6, 19, "Welcome to the Pokemon Center.  How can Nurse Joy assist you?");
  mvprintw(7,19,"Select a Pokemon to heal up! or hit s to save pokemon to storage or t to take from storage");
  for(int i = 0; i < world.pc.inventory.size(); i++){
      mvprintw(8+i,19,"%d. %s HP: %2d Level: %d",i+1,world.pc.inventory.at(i).identifier,world.pc.inventory.at(i).hp,world.pc.inventory.at(i).level);
  }

  if(world.storage.size() > 0){
    mvprintw(world.pc.inventory.size() + 8,19,"Storage:");
    for(int i = 0; i < world.storage.size(); i++){
      mvprintw(8+world.pc.inventory.size()+1+i,19,"%d. %s HP: %2d Level: %d",i+1,world.storage.at(i).identifier,world.storage.at(i).hp,world.storage.at(i).level);
  }
  }

    wrefresh(pokemon_center);
  }
  endwin();
}
Pokemon new_pokemon()
{
  Pokemon p;
  int level;
  int randPokemon = rand()%898+1;
  int p_id = pokemon[randPokemon].id;
  //find the manhatan distance between the
  int x = abs(world.cur_idx[dim_x] - (int)(WORLD_SIZE / 2));
  int y = abs(world.cur_idx[dim_y] - (int)(WORLD_SIZE / 2));
  int distance = x + y;
  if (distance <= 200) {
      if(distance > 1)
      {
        level = rand()%(distance/2) + 1;
      }
      else
      {
        level = 1;
      }
  } else {
    level = rand()%((distance - 200) / 2)+1;
  }
  p.level = level;
  strcpy(p.identifier,pokemon[randPokemon].identifier);
  bool is_shiny = false;
  if(rand()%8192 == 0)
  {
    is_shiny = true;
  }
  //These are the iv stats
  int health = rand()%16;
  int attack = rand()%16;
  int defense = rand()%16;
  int speed = rand()%16;
  int specialAttack = rand()%16;
  int specialDefense = rand()%16;

   if(is_shiny)
  {
    health = 10;
    attack = 10;
    defense = 10;
    speed = 10;
    specialAttack = 10;
    specialDefense = 10;
  }
  int hp_base_stat;
  int attack_base_stat;
  int defense_base_stat;
  int speed_base_stat;
  int specialAttack_base_stat;
  int specialDefense_base_stat;
   for(int i = 0; i < 6552; i++)
  {
    if(pokemon_stats[i].pokemon_id == p_id && pokemon_stats[i].stat_id == 1)
    {
      hp_base_stat = pokemon_stats[i].base_stat;
    }
    if(pokemon_stats[i].pokemon_id == p_id && pokemon_stats[i].stat_id == 2)
    {
      attack_base_stat = pokemon_stats[i].base_stat;
    }
    if(pokemon_stats[i].pokemon_id == p_id && pokemon_stats[i].stat_id == 3)
    {
      defense_base_stat = pokemon_stats[i].base_stat;
    }
    if(pokemon_stats[i].pokemon_id == p_id && pokemon_stats[i].stat_id == 4)
    {
      speed_base_stat = pokemon_stats[i].base_stat;
    }
    if(pokemon_stats[i].pokemon_id == p_id && pokemon_stats[i].stat_id == 5)
    {
      specialAttack_base_stat = pokemon_stats[i].base_stat;
    }
    if(pokemon_stats[i].pokemon_id == p_id && pokemon_stats[i].stat_id == 6)
    {
      specialDefense_base_stat = pokemon_stats[i].base_stat;
    }
  }

   for(int i = 0; i < 1676; i++){
    if(pokemon_types[i].pokemon_id == p_id && pokemon_types[i].slot == 1)
    {
      p.atk_id[0] = pokemon_types[i].type_id;
    }
    if(pokemon_types[i].pokemon_id == p_id && pokemon_types[i].slot == 2)
    {
      p.atk_id[1] = pokemon_types[i].type_id;
    }
  }

  for(int i = 0; i < 899; i++){
    if(species[i].id == p_id)
    {
      p.capture_rate = species[i].capture_rate;
    }
  }

  p.base_speed = speed_base_stat;
  p.hp = ((((hp_base_stat+health)*2)*level)/100)+level + 10;
  p.default_hp = p.hp;
  p.atk= ((((attack_base_stat+attack)*2)*level)/100) + 5;
  p.def = ((((defense_base_stat+defense)*2)*level)/100) + 5;
  p.spd = ((((speed_base_stat+speed)*2)*level)/100) + 5;
  p.spa = ((((specialAttack_base_stat+specialAttack)*2)*level)/100) + 5;
  p.sd = ((((specialDefense_base_stat+specialDefense)*2)*level)/100) + 5;
  int move_id[1];
  vector<int> viable_moves;
  for (int i = 0; i < 528238; i++) {
    if(pokemon_moves[i].pokemon_id == p_id && pokemon_moves[i].pokemon_move_method_id == 1 && pokemon_moves[i].level <= level)
    {
        viable_moves.push_back(pokemon_moves[i].move_id);
    }
  }
  int size = viable_moves.size();
  move_id[0] = viable_moves.at(rand()%size);

  move_id[1] = viable_moves.at(rand()%size);
  //check if move_id[0] is equal to move_id[1]
  if(move_id[0] == move_id[1])
  {
    //if so, find a new move_id[1]
    move_id[1] = viable_moves.at(rand()%size);
  }


  for(int i = 1; i < 845; i++)
  {
    if(moves[i].id == move_id[0])
    {
      p.power[0] = moves[i].power;
      p.accuracy[0] = moves[i].accuracy;
      p.move_priority[0] = moves[i].priority;
      p.type_id = moves[i].type_id;
      strcpy(p.move1, moves[i].identifier);
    }
    if(moves[i].id == move_id[1])
    {
      p.power[1] = moves[i].power;
      if(p.power[1] == 0 || p.power[1] == -1){
        p.power[1] = rand()%100;
      }
      p.accuracy[1] = moves[i].accuracy;
      p.move_priority[1] = moves[i].priority;
      strcpy(p.move2, moves[i].identifier);
    }
  }

  if(p.power[1] == 0 || p.power[1] == -1){
    p.power[1] = rand()%100;
  }
  if(p.power[0] == 0 || p.power[0] == -1){
    p.power[0] = rand()%100;
  }
  pokemon[randPokemon].level = level;
  char gender[10];
  int gen = rand()%2;
  if(gen == 0)
  {
     strcpy(p.gender,"Male");
  }else{
    strcpy(p.gender,"Female");
  }
  char shiny[5];
  if(is_shiny)
  {
    strcpy(shiny,"Yes");
  }else{
    strcpy(shiny,"No");
  }
  return p;
}
Pokemon selectPokemon(){
Pokemon null;
null.hp = 999;
int faint_count = 0;
  for(int i = 0; i < world.pc.inventory.size(); i++)
  {
    if(world.pc.inventory.at(i).hp == 0)
    {
      faint_count++;
    }
  }
  if(faint_count == world.pc.inventory.size())
  {
    mvprintw(0,0,"All of your pokemon are asleep hit esc to continue");
    refresh();

    return null;
  }

  WINDOW *pokemon_select = newwin(12,52,6,18);

  mvprintw(6, 19, "Which pokemon do you want to use?");
  //print out the pokemon in the world.pc inventory and allow the user to select one

  for(int i = 0; i < world.pc.inventory.size(); i++)
  {
    mvprintw(7+i, 19, "%d. %s HP:%2d lvl %d",i+1,world.pc.inventory.at(i).identifier,world.pc.inventory.at(i).hp,world.pc.inventory.at(i).level);

  }

  char choice = getch();
  int choice_int = choice - '0';
  while(choice_int-1 > world.pc.inventory.size()- 1||world.pc.inventory.at(choice_int-1).hp == 0)
  {
    mvprintw(12,19,"Not in bag,enter the number you want to use or the pokemon is sleeping");
    choice_int = getch() - '0';
  }
  Pokemon p = world.pc.inventory.at(choice_int-1);
  world.pc.inventory.erase(world.pc.inventory.begin()+(choice_int-1));
  wrefresh(pokemon_select);
  werase(pokemon_select);
  endwin();
  return p;
}
void escape(WINDOW *pokemon_window,Pokemon fighter, Pokemon wild, int attmpt)
{
  int escapeOdds = ((fighter.spd*32)/((wild.spd/4)%256))+30*attmpt;
            if(rand()%100 < escapeOdds)
            {
              mvprintw(14, 19, "You were able to flee in function");
              wrefresh(pokemon_window);
            }else{
              mvprintw(14, 19, "Flee failed");
              wrefresh(pokemon_window);
          }
}
double type(Pokemon a, Pokemon b,int a_move,int b_move){
 double type = 100.0;
 for(int i = 0; i < 325; i++){
   if(a.atk_id[a_move] == type_efficacy[i].damage_type_id){

     for(int j = 0; j < 325; j++){
       if(b.atk_id[b_move] == type_efficacy[j].target_type_id){
         type = type_efficacy[i].damage_factor;
       }
     }
   }
 }

  return type/100;
}

int hitDamage(Pokemon atk,Pokemon def,int move,int d_move)
{
  double random = rand()%1+.85;
  int chance = rand () % 100;
  int range = rand()%255;
  double critical;

  // 2 percent chance of a critical hit

    if (range < (atk.base_speed / 2))
    {
      critical = 1.5;
    }
    else
    {
      critical = 1;
    }


  double stab;

  if(atk.type_id == def.type_id)
  {
    stab = 1.5;
  }else{
    stab = 1;
  }
  int damage;
  //int damage =(int) (((2*atk.level/5+2)*(atk.power[move]) *(atk.atk/def.def))/50 + 2)*critical*random*stab;
  if(rand()%100 < atk.accuracy[move]){
   damage = (int) ((((((2*atk.level)/5)+2) * atk.power[move]*(atk.atk/atk.def))/50)+2)*critical*random*stab;
  }else{
    damage = 0;
  }
  return damage;
}
void wild_ai(WINDOW *pokemon_window,Pokemon wild,int op_action,char w_moves[2][30],int damage)
{
              if(op_action != 2)
              {
                mvprintw(16, 19, "%s used %s DMG: %2d                     ",wild.identifier,w_moves[op_action],damage);
                wrefresh(pokemon_window);

              }else{

              }
}
void trainer_ai(WINDOW *pokemon_window,Pokemon trainer,Pokemon fighter,int op_action,char t_moves[2][30],int damage)
{
          //mvprintw(0,0,"DAMAGE IS: %d",damage);
          mvprintw(16, 19, "%s used %s DMG: %2d                           ",trainer.identifier,t_moves[op_action],damage);
                wrefresh(pokemon_window);


}

bool is_caught(Pokemon wild){
  int random = rand()%255;
  if(wild.hp == 0 && random < 25)
  {
    return true;
  }else if(random - 25 > wild.capture_rate){
    return false;
  }else{
    return false;
  }
}
void io_battle(Character *aggressor, Character *defender)
{
  Npc *npc;

  if (!(npc = dynamic_cast<Npc *>(aggressor))) {
    npc = dynamic_cast<Npc *>(defender);

  }
//there is a 60% probability that the trainer will get an (n+1)th Pokemon, up to a maximum of 6 Pokemons
  if(rand()%100 < 60 && npc->inventory.size() < 6)
  {
    npc->inventory.push_back(new_pokemon());
  }
  int tPokemon_size = npc->inventory.size();
  WINDOW *pokemon_battle = newwin(12,52,6,18);
  Pokemon tPokemon;
   // wborder
   /*
    mvprintw(6, 19, "You are fighting a trainer");
    mvprintw(7, 19, "the pokemon is %s",npc->inventory.at(0).identifier);
    mvprintw(8, 19, "HP: %d",npc->inventory.at(0).hp);
    */
    int currPoke = 0;
    tPokemon = npc->inventory.at(currPoke);
    //wborder(pokemon_battle, '|', '|', '-', '-', '+', '+', '+', '+');
    mvprintw(7, 19, "Trainer chose %s!", tPokemon.identifier);
    mvprintw(8, 19, "hit space to continue");
    while((getch()) != 32)
    {

    }
    int attmpt = 1;
    int pc_action = 0;
    int op_action = 0;

    Pokemon fighter = selectPokemon();
    if(fighter.hp == 999)
    {
      return;
    }
    char f_moves[2][30];
    strcpy(f_moves[0],fighter.move1);
    strcpy(f_moves[1],fighter.move2);
    char t_moves[2][30];
    strcpy(t_moves[0],tPokemon.move1);
    strcpy(t_moves[1],tPokemon.move2);
    mvprintw(6, 19, "%s | lvl %d", tPokemon.identifier,tPokemon.level);
    mvprintw(7, 19, "HP: %2d", tPokemon.hp);
    mvprintw(10, 19, "%s | lvl %d", fighter.identifier,fighter.level);
    mvprintw(11, 19, "HP: %2d", fighter.hp);
    mvprintw(12, 19, "> %s", f_moves[0]);
    mvprintw(13, 19, "> %s", f_moves[1]);
    wrefresh(pokemon_battle);
    char opt;
    while((opt = getch()) != 27)
    {

        wrefresh(pokemon_battle);
        mvprintw(6, 19, "%s | lvl %d", tPokemon.identifier,tPokemon.level);
        mvprintw(7, 19, "HP: %2d", tPokemon.hp);
        mvprintw(10, 19, "%s | lvl %d", fighter.identifier,fighter.level);
        mvprintw(11, 19, "HP: %2d", fighter.hp);
        mvprintw(12, 19, "> %s", f_moves[0]);
        mvprintw(13, 19, "> %s", f_moves[1]);
        switch(opt)
        {
            case '1':
            pc_action = 0;
            break;
            case '2':
            pc_action = 1;
            break;
            case 's':
            fighter = selectPokemon();
            pc_action = 3;

            break;
        }
        if(fighter.hp == 999)
        {
          return;
        }

        attmpt++;


        //2 percent chance that the tPokemon pokemon will run
      //Determine percentage chances of op_action here;
        //if op action is 2, call trainer_ai with a damage value of 0 since it will automaticall go to run portion of if statement

          op_action = rand()%2    ;



        //Possibly add battle stop condition

        int fight2tPokemon;
        int tPokemon2Fight;


          // the Pokemon whose move has the higher priority (moves.priority) goes first
          if(fighter.move_priority[pc_action] > tPokemon.move_priority[op_action])
          {
            //fighter attacks
            fight2tPokemon = hitDamage(fighter,tPokemon,pc_action,op_action);
            mvprintw(15, 19, "%s used %s DMG: %d                               ",fighter.identifier,f_moves[pc_action],fight2tPokemon);

            tPokemon.hp = tPokemon.hp - fight2tPokemon;
            mvprintw(7, 19, "HP: %2d", tPokemon.hp);
            if(tPokemon.hp <= 0)
            {
              tPokemon.hp = 0;
              mvprintw(14, 19, "%s fainted           ",tPokemon.identifier);
              npc->inventory.at(currPoke).hp = 0;

              if(currPoke+1 < npc->inventory.size())
              {
                currPoke++;
              tPokemon = npc->inventory.at(currPoke);
              mvprintw(14, 19, "Trainer chose %s",tPokemon.identifier);
              mvprintw(6, 19, "%s | lvl %d", tPokemon.identifier,tPokemon.level);
              mvprintw(7, 19, "HP: %2d", tPokemon.hp);
            }else{
              wrefresh(pokemon_battle);
              clear();
              mvprintw(13,19,"%s fainted",tPokemon.identifier);
              int loot = 0;
                for(int i = 0; i < tPokemon_size; i++)
                {
                  loot += rand() % 100;
                }
                world.pc.pokebux += loot;
                mvprintw(14,19,"You have defeated this trainer press space to continue  +Pokebux: %d",loot);
              world.pc.inventory.push_back(fighter);
              npc->defeated = 1;
              if (npc->ctype == char_hiker || npc->ctype == char_rival) {
                npc->mtype = move_wander;
            }
              while((getch()) != 32)
              {

              }
              endwin();
              return;
            }
          }else{
            tPokemon2Fight = hitDamage(tPokemon,fighter,op_action,pc_action);
            trainer_ai(pokemon_battle,tPokemon,fighter,op_action,t_moves,tPokemon2Fight);

            wrefresh(pokemon_battle);
            refresh();
            fighter.hp -= tPokemon2Fight;
            if(fighter.hp <= 0)
            {
              fighter.hp = 0;
              mvprintw(14, 19, "%s fainted",fighter.identifier);
              world.pc.inventory.push_back(fighter);
              fighter = selectPokemon();
              if(fighter.hp == 999)
              {
                clear();
                mvprintw(14,19,"All your pokemon are asleep");
                mvprintw(15,19,"You lost, press space to close");
                while((getch()) != 32)
                {

                }
                endwin();
                npc->defeated = 1;
                if (npc->ctype == char_hiker || npc->ctype == char_rival) {
                  npc->mtype = move_wander;
                }
                return;
              }

            }else{
              mvprintw(11, 19, "HP: %2d", fighter.hp);
            }
          }



        }else if(pc_action != 3)
        {

            tPokemon2Fight = hitDamage(tPokemon,fighter,op_action,pc_action);

            trainer_ai(pokemon_battle,tPokemon,fighter,op_action,t_moves,tPokemon2Fight);

            fighter.hp = fighter.hp - tPokemon2Fight;
            if(fighter.hp <= 0)
            {
              fighter.hp = 0;
              mvprintw(14, 19, "%s fainted",fighter.identifier);
              world.pc.inventory.push_back(fighter);
              fighter = selectPokemon();
              if(fighter.hp == 999)
              {
                clear();
                mvprintw(14,19, "All your pokemon are asleep");
                mvprintw(15,19,"You lost, press space to close");
                npc->defeated = 1;
                if (npc->ctype == char_hiker || npc->ctype == char_rival) {
                 npc->mtype = move_wander;
               }
                while((getch()) != 32)
                {

                }
                endwin();
                return;
              }

            }else{
              mvprintw(11, 19, "HP: %2d", fighter.hp);
              fight2tPokemon = hitDamage(fighter,tPokemon,pc_action,op_action);
              mvprintw(15, 19, "%s used %s DMG: %d                             ",fighter.identifier, f_moves[pc_action],fight2tPokemon);
              mvprintw(7, 19, "HP: %2d", tPokemon.hp);

              tPokemon.hp = tPokemon.hp - fight2tPokemon;
              if(tPokemon.hp <= 0)
              {
                tPokemon.hp = 0;
                mvprintw(14, 19, "%s fainted",tPokemon.identifier);
                npc->inventory.at(currPoke).hp = 0;

                if(currPoke+1 < npc->inventory.size())
                {
                currPoke++;
                tPokemon = npc->inventory.at(currPoke);
                mvprintw(14, 19, "Trainer chose %s",tPokemon.identifier);
                mvprintw(6, 19, "%s | lvl %d", tPokemon.identifier,tPokemon.level);
                mvprintw(7, 19, "HP: %2d", tPokemon.hp);


              }else{
                wrefresh(pokemon_battle);
                clear();
                int loot = 0;
                for(int i = 0; i < tPokemon_size; i++)
                {
                  loot += rand() % 100;
                }
                world.pc.pokebux += loot;
                mvprintw(14,19,"You have defeated this trainer press space to continue  +Pokebux: %d",loot);
                world.pc.inventory.push_back(fighter);
                npc->defeated = 1;
                if (npc->ctype == char_hiker || npc->ctype == char_rival) {
                    npc->mtype = move_wander;
                }
                while((getch()) != 32)
                {

                }
                endwin();
                return;
              }
              }else{
                mvprintw(7, 19, "HP: %2d", tPokemon.hp);
                wrefresh(pokemon_battle);
              }
            }


        }






        wrefresh(pokemon_battle);
    }

     wrefresh(pokemon_battle);
    while((getch()) != 27)
    {

    }
    werase(pokemon_battle);
    endwin();
}

//this function will compare the speed of two pokemon and return the faster one
void pokemon_wild(){
    Pokemon wild = new_pokemon();
    WINDOW *pokemon_window = newwin(12,52,6,18);
   wborder(pokemon_window, '|', '|', '-', '-', '+', '+', '+', '+');
    mvprintw(6, 19, "A wild %s appeared!", wild.identifier);
    mvprintw(7, 19, "Level: %d", wild.level);
    mvprintw(8, 19, "Pokemon moves: %s Power: %d, %s Power: %d", wild.move1,wild.power[0], wild.move2,wild.power[1]);
    mvprintw(9, 19, "");
    mvprintw(10, 19, "Pokemon Stats:");
    mvprintw(11, 19, "HP: %2d, Attack: %d, Defense: %d", wild.hp, wild.atk, wild.def);
    mvprintw(12, 19, "Speed: %d, Special Attack: %d, Special Defense: %d", wild.spd, wild.spa, wild.sd);
    mvprintw(13, 19, "Gender: %s",wild.gender);
    mvprintw(14, 19, "Press m to continue");


    wrefresh(pokemon_window);
    while((getch()) != 'm')
    {

    }
    werase(pokemon_window);
     wrefresh(pokemon_window);
    int attmpt = 1;
    int pc_action = 0;
    int op_action = 0;
    Pokemon fighter = selectPokemon();
    if(fighter.hp == 999)
    {
      return;
    }
    char f_moves[2][30];
    strcpy(f_moves[0],fighter.move1);
    strcpy(f_moves[1],fighter.move2);
    char w_moves[2][30];
    strcpy(w_moves[0],wild.move1);
    strcpy(w_moves[1],wild.move2);
    mvprintw(6, 19, "%s | lvl %d", wild.identifier,wild.level);
    mvprintw(7, 19, "HP: %2d", wild.hp);
    mvprintw(10, 19, "%s | lvl %d", fighter.identifier,fighter.level);
    mvprintw(11, 19, "HP: %2d", fighter.hp);
    mvprintw(12, 19, "> %s", f_moves[0]);
    mvprintw(13, 19, "> %s", f_moves[1]);
    wrefresh(pokemon_window);
    char opt;

    while((opt = getch()) != 27)
    {

        wrefresh(pokemon_window);
        switch(opt)
        {
          case 'f':

            pc_action = 2;
            escape(pokemon_window,fighter,wild,attmpt);
            break;
            case '1':
            pc_action = 0;
            break;
            case '2':
            pc_action = 1;
            break;
            case 's':
            fighter = selectPokemon();
            pc_action = 3;

            break;
        }
        if(fighter.hp == 999)
        {
          return;
        }


        attmpt++;


        //2 percent chance that the wild pokemon will run
      //Determine percentage chances of op_action here;
        //if op action is 2, call wild_ai with a damage value of 0 since it will automaticall go to run portion of if statement
        if(rand()%100 < 2)
        {
          op_action = 2;
          wild_ai(pokemon_window,wild,op_action,w_moves,0);
          while((getch()) != 32)
          {

          }
          world.pc.inventory.push_back(fighter);
          clear();
          mvprintw(18,19,"%s ran away press space to continue",wild.identifier);
          wrefresh(pokemon_window);
          while((getch()) != 32)
          {

          }
          endwin();

          return;
        }else {
          op_action = rand()%2    ;
        }


        //Possibly add battle stop condition

        int fight2Wild;
        int Wild2Fight;


          // the Pokemon whose move has the higher priority (moves.priority) goes first
          if(fighter.move_priority[pc_action] > wild.move_priority[op_action])
          {
            //fighter attacks
            fight2Wild = hitDamage(fighter,wild,pc_action,op_action);
            mvprintw(15, 19, "%s used %s DMG: %d                              ",fighter.identifier,f_moves[pc_action],fight2Wild);

            wild.hp = wild.hp - fight2Wild;
            mvprintw(7, 19, "HP: %2d", wild.hp);
            if(wild.hp <= 0)
            {
              wild.hp = 0;
              clear();
              mvprintw(13, 19, "%s fainted",wild.identifier);
                world.pc.inventory.push_back(fighter);
                if(world.pc.pokeballs < 0){
                  mvprintw(14,19,"You have no pokeballs left");
                }else if(is_caught(wild)){
                if(world.pc.inventory.size() < 6)
                {
                    world.pc.inventory.push_back(wild);
                    mvprintw(14,19,"You won press space to close adding %s to your bag press space to close",wild.identifier);
                }else if (world.storage.size() < 6 && world.pc.inventory.size() == 6){
                    world.storage.push_back(wild);
                     mvprintw(14,19,"You won press space to close adding %s to storage press space to close",wild.identifier);
                }}else{
                    mvprintw(14,19,"%s broke free press space to close",wild.identifier);
                }
                wrefresh(pokemon_window);

                while((getch()) != 32)
                {

                }

                endwin();
                return;

            }
            Wild2Fight = hitDamage(wild,fighter,op_action,pc_action);

            wild_ai(pokemon_window,wild,op_action,w_moves,Wild2Fight);

            wrefresh(pokemon_window);
            refresh();
            fighter.hp -= Wild2Fight;
            if(fighter.hp <= 0)
            {
              fighter.hp = 0;
              clear();
              mvprintw(14, 19, "%s fainted",fighter.identifier);
              mvprintw(15,19,"You lost press space to close");
              world.pc.inventory.push_back(fighter);
              while((getch()) != 32)
              {

              }

              endwin();
              return;

            }else{
              mvprintw(11, 19, "HP: %2d", fighter.hp);
            }
        }else
        {

            Wild2Fight = hitDamage(wild,fighter,op_action,pc_action);
            wild_ai(pokemon_window,wild,op_action,w_moves,Wild2Fight);

            fighter.hp = fighter.hp - Wild2Fight;
            if(fighter.hp <= 0)
            {
              fighter.hp = 0;
              clear();
              mvprintw(14, 19, "%s fainted",fighter.identifier);
              mvprintw(15,19,"You lost press space to close");
              world.pc.inventory.push_back(fighter);
              wrefresh(pokemon_window);
              while((getch()) != 32)
              {

              }

              endwin();
              return;

            }else{
              mvprintw(11, 19, "HP: %2d", fighter.hp);
            }

            fight2Wild = hitDamage(fighter,wild,pc_action,op_action);
            mvprintw(15, 19, "%s used %s DMG: %d                              ",fighter.identifier, f_moves[pc_action],fight2Wild);
            mvprintw(7, 19, "HP: %2d", wild.hp);

            wild.hp = wild.hp - fight2Wild;
            if(wild.hp <= 0)
            {
              wild.hp = 0;
              //mvprintw(7, 19, "HP: %d", wild.hp);
              clear();
              mvprintw(13, 19, "%s fainted",wild.identifier);
              world.pc.inventory.push_back(fighter);
              if(world.pc.pokeballs < 0){
                mvprintw(14,19,"You are out of pokeballs press space to close");
              }else if(is_caught(wild)){

              if(world.pc.inventory.size() < 6)
              {

                  world.pc.inventory.push_back(wild);
                  mvprintw(14,19,"You won press space to close adding %s to your bag press space to close",wild.identifier);
              }else{
                world.storage.push_back(wild);
                mvprintw(14,19,"You won press space to close adding %s to storage press space to close",wild.identifier);
              }
              }else{
                mvprintw(14,19,"%s broke free and ran away",wild.identifier);
              }

              wrefresh(pokemon_window);
              while((getch()) != 32)
              {

              }

              endwin();
              return;


            }else{
              mvprintw(7, 19, "HP: %2d", wild.hp);
              wrefresh(pokemon_window);
            }
        }



        wrefresh(pokemon_window);

    }

}

void choose_pokemon()
{
  WINDOW *choose_pokemon_window = newwin(12,52,6,18);
  mvprintw(6, 19, "Choose a Pokemon, press 1 2 or 3");
  world.pc.pokeballs = 6;
  world.pc.revives = 3;
  world.pc.potions = 6;
  for(int i = 0; i < 3; i++)
  {
    Pokemon p1 = new_pokemon();
    Pokemon p2 = new_pokemon();
    Pokemon p3 = new_pokemon();
    //print each pokemon stats
    mvprintw(7, 19, "1. %s 2. %s 3. %s",p1.identifier,p2.identifier,p3.identifier);
    mvprintw(8, 19, "HP: %2d | %d | %d",p1.hp,p2.hp,p3.hp);
    mvprintw(9, 19, "Attack: %d | %d | %d |",p1.atk,p2.atk,p3.atk);
    mvprintw(10, 19, "Defense: %d | %d | %d ",p1.def,p2.def,p3.def);
    mvprintw(11, 19, "Speed: %d | %d | %d",p1.spd,p2.spd,p3.spd);
    mvprintw(12, 19, "Special Attack: %d | %d | %d",p1.spa,p2.spa,p3.spa);
    mvprintw(13, 19, "Special Defense: %d | %d | %d",p1.sd,p2.sd,p3.sd);
    wrefresh(choose_pokemon_window);
    int choice = getch();

    switch(choice)
    {
      case 49:
        mvprintw(14, 19, "You chose %s",p1.identifier);
        world.pc.inventory.push_back(p1);
        break;
      case 50:
        mvprintw(14, 19, "You chose %s",p2.identifier);
        world.pc.inventory.push_back(p2);
        break;
      case 51:
        mvprintw(14, 19, "You chose %s",p3.identifier);
        world.pc.inventory.push_back(p3);
        break;
    }
  werase(choose_pokemon_window);
  }
  mvprintw(14, 19, "Press any arrow key to close");
  endwin();
  refresh();
}
uint32_t move_pc_dir(uint32_t input, pair_t dest)
{
  dest[dim_y] = world.pc.pos[dim_y];
  dest[dim_x] = world.pc.pos[dim_x];

  switch (input) {
  case 1:
  case 2:
  case 3:
    dest[dim_y]++;
    break;
  case 4:
  case 5:
  case 6:
    break;
  case 7:
  case 8:
  case 9:
    dest[dim_y]--;
    break;
  }
  switch (input) {
  case 1:
  case 4:
  case 7:
    dest[dim_x]--;
    break;
  case 2:
  case 5:
  case 8:
    break;
  case 3:
  case 6:
  case 9:
    dest[dim_x]++;
    break;
  case '>':
    if (world.cur_map->map[world.pc.pos[dim_y]][world.pc.pos[dim_x]] ==
        ter_mart) {
      io_pokemart();
    }
    if (world.cur_map->map[world.pc.pos[dim_y]][world.pc.pos[dim_x]] ==
        ter_center) {
      io_pokemon_center();
    }
    break;
  }

  if ((world.cur_map->map[dest[dim_y]][dest[dim_x]] == ter_exit) &&
      (input == 1 || input == 3 || input == 7 || input == 9)) {
    // Exiting diagonally leads to complicated entry into the new map
    // in order to avoid INT_MAX move costs in the destination.
    // Most easily solved by disallowing such entries here.
    return 1;
  }

  if(world.cur_map->map[dest[dim_y]][dest[dim_x]] == ter_grass){
      bool chance = (rand()%100) < 10;
      if(chance){
        pokemon_wild();
      }

  }
  /*
  If the desitnation cell on the terrain map is grass
  10% chance that a pokemon battle with a random pokemon will occur
  TO DO: Battle function that opens up a new window.

  */
  //world.cur_idx[dim_x] && world.cur_idx[dim_y] check distance from origin

  if (world.cur_map->cmap[dest[dim_y]][dest[dim_x]]) {
    if (dynamic_cast<Npc *>(world.cur_map->cmap[dest[dim_y]][dest[dim_x]]) &&
        ((Npc *) world.cur_map->cmap[dest[dim_y]][dest[dim_x]])->defeated) {
      // Some kind of greeting here would be nice
      return 1;
    } else if (dynamic_cast<Npc *>
               (world.cur_map->cmap[dest[dim_y]][dest[dim_x]])) {
      io_battle(&world.pc, world.cur_map->cmap[dest[dim_y]][dest[dim_x]]);
      // Not actually moving, so set dest back to PC position
      dest[dim_x] = world.pc.pos[dim_x];
      dest[dim_y] = world.pc.pos[dim_y];
    }
  }

  if (move_cost[char_pc][world.cur_map->map[dest[dim_y]][dest[dim_x]]] ==
      INT_MAX) {
    return 1;
  }

  return 0;
}

void io_teleport_world(pair_t dest)
{
  int x, y;

  world.cur_map->cmap[world.pc.pos[dim_y]][world.pc.pos[dim_x]] = NULL;

  mvprintw(0, 0, "Enter x [-200, 200]: ");
  refresh();
  echo();
  curs_set(1);
  mvscanw(0, 21, "%d", &x);
  mvprintw(0, 0, "Enter y [-200, 200]:          ");
  refresh();
  mvscanw(0, 21, "%d", &y);
  refresh();
  noecho();
  curs_set(0);

  if (x < -200) {
    x = -200;
  }
  if (x > 200) {
    x = 200;
  }
  if (y < -200) {
    y = -200;
  }
  if (y > 200) {
    y = 200;
  }

  x += 200;
  y += 200;

  world.cur_idx[dim_x] = x;
  world.cur_idx[dim_y] = y;

  new_map(1);
  io_teleport_pc(dest);
}
void revivePokemon()
{
  int pokeNum = getch() - '0';
  if(pokeNum <= world.pc.inventory.size() && world.pc.inventory.at(pokeNum - 1).hp == 0 && world.pc.revives > 0)
  {
    world.pc.revives = world.pc.revives - 1;
    world.pc.inventory.at(pokeNum - 1).hp = world.pc.inventory.at(pokeNum - 1).default_hp/ 2;
  }

}
void pokemonPotion(){
  int pokeNum = getch() - '0';
  if((pokeNum <= world.pc.inventory.size())&&world.pc.inventory.at(pokeNum -1).hp != 0 && (world.pc.inventory.at(pokeNum - 1).hp < world.pc.inventory.at(pokeNum - 1).default_hp) && world.pc.potions > 0){
    world.pc.inventory.at(pokeNum - 1).hp += 20;
    if(world.pc.inventory.at(pokeNum - 1).hp > world.pc.inventory.at(pokeNum - 1).default_hp)
    {
      world.pc.inventory.at(pokeNum - 1).hp = world.pc.inventory.at(pokeNum - 1).default_hp;
    }
    world.pc.potions = world.pc.potions - 1;

  }
}
void printBag()
{
  //print all pokemon in the players inventory
  WINDOW *player_inventory = newwin(12,52,6,18);
  int opt;
  while((opt = getch()) != 27)
  {
    mvprintw(6,19,"PokeBucks: %d |Pokemon Balls: %d |Pokemon Potion: %d |Revives: %d",world.pc.pokebux,world.pc.pokeballs,world.pc.potions,world.pc.revives);
    for(int i = 0; i < world.pc.inventory.size(); i++){
      mvprintw(7+i,19,"%d. %s HP: %2d Level: %d",i+1,world.pc.inventory.at(i).identifier,world.pc.inventory.at(i).hp,world.pc.inventory.at(i).level);
    }
    wrefresh(player_inventory);
    switch(opt){
      case 'r':
      //revive a pokemon;
      mvprintw(0, 0, "Select a pokemon to revive");
      revivePokemon();
      break;
      case 'p':
      //add health to pokemon
      mvprintw(0, 0, "Select a pokemon to give potion to");
      pokemonPotion();
      break;

    }
    mvprintw(6,19,"Pokemon Balls: %d Pokemon Potion: %d Revives: %d",world.pc.pokeballs,world.pc.potions,world.pc.revives);
    for(int i = 0; i < world.pc.inventory.size(); i++){
      mvprintw(7+i,19,"%d. %s HP: %2d Level: %d",i+1,world.pc.inventory.at(i).identifier,world.pc.inventory.at(i).hp,world.pc.inventory.at(i).level);
    }

    wrefresh(player_inventory);
  }
  endwin();
  refresh();

}
bool choose = false;
void io_handle_input(pair_t dest)
{
  if(!choose)
  {
    choose_pokemon();
    choose = true;
  }
  uint32_t turn_not_consumed;
  int key;

  do {
    switch (key = getch()) {
    case '7':
    case 'y':
    case KEY_HOME:
      turn_not_consumed = move_pc_dir(7, dest);
      break;
    case '8':
    case 'k':
    case KEY_UP:
      turn_not_consumed = move_pc_dir(8, dest);
      break;
    case '9':
    case 'u':
    case KEY_PPAGE:
      turn_not_consumed = move_pc_dir(9, dest);
      break;
    case '6':
    case 'l':
    case KEY_RIGHT:
      turn_not_consumed = move_pc_dir(6, dest);
      break;
    case '3':
    case 'n':
    case KEY_NPAGE:
      turn_not_consumed = move_pc_dir(3, dest);
      break;
    case '2':
    case 'j':
    case KEY_DOWN:
      turn_not_consumed = move_pc_dir(2, dest);
      break;
    case '1':
    case 'b':
    case KEY_END:
      turn_not_consumed = move_pc_dir(1, dest);
      break;
    case '4':
    case 'h':
    case KEY_LEFT:
      turn_not_consumed = move_pc_dir(4, dest);
      break;
    case '5':
    case ' ':
    case '.':
    case KEY_B2:
      dest[dim_y] = world.pc.pos[dim_y];
      dest[dim_x] = world.pc.pos[dim_x];
      turn_not_consumed = 0;
      break;
    case '>':
      turn_not_consumed = move_pc_dir('>', dest);
      break;
    case 'Q':
      dest[dim_y] = world.pc.pos[dim_y];
      dest[dim_x] = world.pc.pos[dim_x];
      world.quit = 1;
      turn_not_consumed = 0;
      break;
      break;
    case 't':
      /* Teleport the PC to a random place in the map.              */
      io_teleport_pc(dest);
      turn_not_consumed = 0;
      break;
    case 'T':
      /* Teleport the PC to any map in the world.                   */
      io_teleport_world(dest);
      turn_not_consumed = 0;
      break;
    case 'm':
      io_list_trainers();
      turn_not_consumed = 1;
      break;
    case 'B':
      printBag();
      turn_not_consumed = 1;
    case 'q':
      /* Demonstrate use of the message queue.  You can use this for *
       * printf()-style debugging (though gdb is probably a better   *
       * option.  Not that it matters, but using this command will   *
       * waste a turn.  Set turn_not_consumed to 1 and you should be *
       * able to figure out why I did it that way.                   */
      io_queue_message("This is the first message.");
      io_queue_message("Since there are multiple messages, "
                       "you will see \"more\" prompts.");
      io_queue_message("You can use any key to advance through messages.");
      io_queue_message("Normal gameplay will not resume until the queue "
                       "is empty.");
      io_queue_message("Long lines will be truncated, not wrapped.");
      io_queue_message("io_queue_message() is variadic and handles "
                       "all printf() conversion specifiers.");
      io_queue_message("Did you see %s?", "what I did there");
      io_queue_message("When the last message is displayed, there will "
                       "be no \"more\" prompt.");
      io_queue_message("Have fun!  And happy printing!");
      io_queue_message("Oh!  And use 'Q' to quit!");

      dest[dim_y] = world.pc.pos[dim_y];
      dest[dim_x] = world.pc.pos[dim_x];
      turn_not_consumed = 0;
      break;
    default:
      /* Also not in the spec.  It's not always easy to figure out what *
       * key code corresponds with a given keystroke.  Print out any    *
       * unhandled key here.  Not only does it give a visual error      *
       * indicator, but it also gives an integer value that can be used *
       * for that key in this (or other) switch statements.  Printed in *
       * octal, with the leading zero, because ncurses.h lists codes in *
       * octal, thus allowing us to do reverse lookups.  If a key has a *
       * name defined in the header, you can use the name here, else    *
       * you can directly use the octal value.                          */
      mvprintw(0, 0, "Unbound key: %#o ", key);
      turn_not_consumed = 1;
    }
    refresh();
  } while (turn_not_consumed);
}
